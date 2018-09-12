using System.Collections;
using System.Collections.Generic;
using UnityEngine;

// В каком формате должны записываться полки?
// 1. Положение в пространстве, положение центра, углы
// 2. Расстояние между юнитами в полку
// 3. Количество юнитов в первой линии
// 4. Тип построения
// 5. Тип распределения разных солдат по полку
// ДОПОЛНИТЕЛЬНО: Начальное количество солдат в полку

public class loadRegiment : MonoBehaviour
{
    public int numReg;
    //1. Положение в пространстве

    //2. Растояние между юнитами в полку

    //3. Количество юнитов в первой линии

    //4. Тип построения

    //5. Тип распределения сол
    
    
    private int temp;
    
    private int[] com;
    public int[] num;
    public float[] x;
    public float[] y;
    public float[] z;
    public float[] angle;
    public int[] size;
    public int[] w;

    
    /// <summary>
    /// Четное число - тип солдат - их количество
    /// </summary>
    public List<mypair>[] sol;


    public loadRegiment(int n)
    {
        temp = 0;
        com = new int[n];
        num = new int[n];
        x = new float[n];
        y = new float[n];
        z = new float[n];
        angle = new float[n];
        size = new int[n];
        w = new int[n];


        /// Тип солдата по номеру в списке типов солдат
  

        sol= new List<mypair>[n];
    }

    public void addregiment(string[] a)
    {
        Debug.Log(" addRegiment ");
        com[temp] = int.Parse(a[0]);
        num[temp] = int.Parse(a[1]);
        x[temp] = float.Parse(a[2]);
        y[temp] = float.Parse(a[3]);
        z[temp] = float.Parse(a[4]);
        angle[temp] = float.Parse(a[5]);
        size[temp] = int.Parse(a[6]);
        w[temp] = int.Parse(a[7]);


        sol[temp] = new List<mypair>();

        int p = 8;
        for (int t = p; t < a.Length; t +=2)
        {
            mypair tt;
            tt.id = int.Parse(a[t]);
            tt.num = int.Parse(a[(t + 1)]);
            sol[temp].Add(tt);
        }

        temp++;
    }

    public void putregiment(int n)
    {

        //mainStatic.regiment_stat[n].regDistanseLine = w[n];
        mainStatic.regiment_stat[n].soldier_count = size[n];
        mainStatic.regiment_stat[n].angle = angle[n];

        mainStatic.regiment_stat[n].stateFighting = false;
        mainStatic.regiment_stat[n].command = com[n];


        mainStatic.regiment_stat[n].soldiers = new List<UnitSoldiers>();
        mainStatic.regiment_stat[n].officers = new List<UnitOfficer>();

        mainStatic.regiment_stat[n].targPos = new List<Vector3>();
        // mainStatic.regiment_stat[n].soldiers = new UnitSoldiers[size[n]];
    }


    public struct mypair
    {
        public int id;
        public int num;

    }

}


