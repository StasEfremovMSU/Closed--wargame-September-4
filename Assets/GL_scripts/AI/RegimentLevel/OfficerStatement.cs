using System.Collections;
using System.Collections.Generic;
using UnityEngine;

public static class OfficerStatement  {
    /// <summary>
    ///  Вызывается из офицера
    /// </summary>
    /// <param name="?">Номер построения, под контролем офицера</param>
	static public void MakeOfficerStatement (int num, int of_num) {
        switch (mainStatic.regiment_stat[num].officers[of_num].statement)
        {
            case (1):
                // Случай атаковать ближайшего врага. Если в ближайших 2х квадратах есть противник, то повернуться к нему и атаковать
                int t = func.getRegNear(num, of_num);
                //Debug.Log(t);
                int res = 0;
                if (t != 99999)
                {
                    res = mainStatic.makeRegimentRotation(num, mainStatic.regiment_stat[t].regPosition.x, mainStatic.regiment_stat[t].regPosition.z);
                }
                if (res == 1)
                {
                    //mainStatic.regiment_stat[num].
                    mainStatic.makeRegimentMove(num, 4.0f);
                    mainStatic.updateRegimentPosition(num);
                }

                if ((mainStatic.regiment_stat[num].regPosition - mainStatic.regiment_stat[t].regPosition).magnitude < 20)
                {
                    
                }


                break;
            case (2):
                // Стоять на месте обороняясь

                break;

            

        }
		
	}
	
}
