using System.Collections;
using System.Collections.Generic;
using UnityEngine;

/// <summary>
/// Здесь офицер полка пытается понять, что за фигня творится вокруг на самом деле
/// </summary>

public class LowOwnOpin {

    // Параметры, которые определить мозг офицера
    // его команды

    public weigt trueStand;

    public weigt trueShooting;

    public weigt trueAtack;

    public weigt trueRotate;

    public weigt trueRebuilding;


    /// <summary>
    /// Полк, который надо атаковать
    /// </summary>
    public int target_atack_reg;

    /// <summary>
    /// Полк, в который надо атаковать
    /// </summary>
    public int target_shooting;

    


    public void targ_atack()
    {
        /// Полк находится не в бою
        /// Если видит тыл, или фланг то атаковать того
        /// 
        /// Уже не держит строй, атаковать ближайшего в зоне видимости
         

    }

    public void targ_shooting()
    {
        /// Обращается к целям для стрельбы, если они не годятся - то ближайший
    }


    // Блок с принятием решений. Было бы неплохо здесь использовать нейросети

    // Если перед тобой рассеянный полк, и нет угрозы боковых ударов - атаковать

    // Если перед тобой фланг или тыл противника - атакуй

    // Если к тебе приближаются кавалеристы - выстави копейщиков или мечников, убери стрелков

    // Если убыль командиров в армии - убери командиров в зад полка

    // Если твоя группа полков идет, врага рядом нет - иди по координатам цепи

    // Если твоя группа полков атакует, иди по координатам цели и атакуй

    // Если группа стоит, лучники по вам стреляют, то разреди ряды

    // Если группа получила приказ на движение - собери вместе ряды и иди

    // Если с двух боков враги - отходи назад

    // Если с трех или 4 сторон враги - атакуй вперед


    public void maketrueStand()
    {
        
    }


    public void maketrueAtack()
    {
        

    }

    public void  maketrueRebuilding()
    {
        if (MyBabiesUnderBows())
        {
            trueRebuilding.weight += 80;
            trueAtack.weight += 5;
        }


    }

    public void maketrueRotate()
    {

    }

    public void maketrueShooting()
    {


    }



    ////////////////////////////////////     Анализ состояния своего полка      \\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\

    /// <summary>
    /// Верно, если полк под сильным обстрелом лучников
    /// </summary>
    /// <returns></returns>
    public bool MyBabiesUnderBows()
    {
        bool res = false;

        return res;
    }

    /// <summary>
    /// Верно, если полк рассеян после драки
    /// </summary>
    /// <returns></returns>
    public bool MyRegimentChaotic()
    {
        bool res = false;

        return res;
    }

    /// <summary>
    /// Верно, полк ведет стрельбу
    /// </summary>
    /// <returns></returns>
    public bool MyBowsWorking()
    {
        bool res = false;

        return res;
    }



    ////////////////////////////////////     Анализ видимых врагов      \\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\

    /// <summary>
    /// Верно, если в зоне контроля полка фланг врага
    /// </summary>
    /// <returns></returns>
    public bool SeeDirectFlang()
    {
        bool res = false;

        return res;
    }

    /// <summary>
    ///  Верно, если в зоне контроля полка находится рассеянный полк
    /// </summary>
    public bool SeeDirecPanic ()
    {
        bool res = false;

        return res;
    }

    /// <summary>
    /// Верно, если в зоне контроля, не готового к кавалерийской атаке полка находится вражеская кавалерия 
    /// </summary>
    public bool SeeTakeYourSpearVsCavalry ()
    {
        bool res = false;

        return res;
    }

    /// <summary>
    ///  Верно, если сбоку или сзади от полка находятся полки врага, и между ними и этим нет других полков
    /// </summary>
    public bool SeeFlangDangerous ()
    {
        bool res = false;

        return res;
    }

    /// <summary>
    ///  Если с двух сторон враги
    /// </summary>
    public bool SeeBothSideDiedForYou ()
    {
        bool res = false;

        return res;
    }

    
    /////////////////////////////////////////    Анализ группы, которой состоит полк \\\\\\\\\\\\\\\\\\\\\\\\\\\\\\


    /// <summary>
    ///  Если группа движется в атаку
    /// </summary>
    public bool GroupMakeWay()
    {
        bool res = false;

        return res;
    }

    /// <summary>
    ///  Если в группе убыль командиров
    /// </summary>
    public bool GroupSpendLeaders()
    {
        bool res = false;

        return res;
    }

    /// <summary>
    ///  Если группа атакует скрытно
    /// </summary>
    public bool GroupStels()
    {
        bool res = false;

        return res;
    }

    /// <summary>
    ///  Если в группе большие потери
    /// </summary>
    public bool HalfGroupDied()
    {
        bool res = false;

        return res;
    }
	
}
